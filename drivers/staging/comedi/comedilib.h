/*
    linux/include/comedilib.h
    header file for kcomedilib

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998-2001 David A. Schleef <ds@schleef.org>

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

#ifndef _LINUX_COMEDILIB_H
#define _LINUX_COMEDILIB_H

#include "comedi.h"

/* Kernel internal stuff.  Needed by real-time modules and such. */

#ifndef __KERNEL__
#error linux/comedilib.h should not be included by non-kernel-space code
#endif

/* exported functions */

#ifndef KCOMEDILIB_DEPRECATED

typedef void comedi_t;

/* these functions may not be called at real-time priority */

comedi_t *comedi_open(const char *path);
int comedi_close(comedi_t *dev);

/* these functions may be called at any priority, but may fail at
   real-time priority */

int comedi_lock(comedi_t *dev, unsigned int subdev);
int comedi_unlock(comedi_t *dev, unsigned int subdev);

/* these functions may be called at any priority, but you must hold
   the lock for the subdevice */

int comedi_loglevel(int loglevel);
void comedi_perror(const char *s);
char *comedi_strerror(int errnum);
int comedi_errno(void);
int comedi_fileno(comedi_t *dev);

int comedi_cancel(comedi_t *dev, unsigned int subdev);
int comedi_register_callback(comedi_t *dev, unsigned int subdev,
	unsigned int mask, int (*cb) (unsigned int, void *), void *arg);

int comedi_command(comedi_t *dev, comedi_cmd *cmd);
int comedi_command_test(comedi_t *dev, comedi_cmd *cmd);
int comedi_trigger(comedi_t *dev, unsigned int subdev, comedi_trig *it);
int __comedi_trigger(comedi_t *dev, unsigned int subdev, comedi_trig *it);
int comedi_data_write(comedi_t *dev, unsigned int subdev, unsigned int chan,
	unsigned int range, unsigned int aref, lsampl_t data);
int comedi_data_read(comedi_t *dev, unsigned int subdev, unsigned int chan,
	unsigned int range, unsigned int aref, lsampl_t *data);
int comedi_data_read_hint(comedi_t *dev, unsigned int subdev,
	unsigned int chan, unsigned int range, unsigned int aref);
int comedi_data_read_delayed(comedi_t *dev, unsigned int subdev,
	unsigned int chan, unsigned int range, unsigned int aref,
	lsampl_t *data, unsigned int nano_sec);
int comedi_dio_config(comedi_t *dev, unsigned int subdev, unsigned int chan,
	unsigned int io);
int comedi_dio_read(comedi_t *dev, unsigned int subdev, unsigned int chan,
	unsigned int *val);
int comedi_dio_write(comedi_t *dev, unsigned int subdev, unsigned int chan,
	unsigned int val);
int comedi_dio_bitfield(comedi_t *dev, unsigned int subdev, unsigned int mask,
	unsigned int *bits);
int comedi_get_n_subdevices(comedi_t *dev);
int comedi_get_version_code(comedi_t *dev);
const char *comedi_get_driver_name(comedi_t *dev);
const char *comedi_get_board_name(comedi_t *dev);
int comedi_get_subdevice_type(comedi_t *dev, unsigned int subdevice);
int comedi_find_subdevice_by_type(comedi_t *dev, int type, unsigned int subd);
int comedi_get_n_channels(comedi_t *dev, unsigned int subdevice);
lsampl_t comedi_get_maxdata(comedi_t *dev, unsigned int subdevice, unsigned
	int chan);
int comedi_get_n_ranges(comedi_t *dev, unsigned int subdevice, unsigned int
	chan);
int comedi_do_insn(comedi_t *dev, comedi_insn *insn);
int comedi_poll(comedi_t *dev, unsigned int subdev);

/* DEPRECATED functions */
int comedi_get_rangetype(comedi_t *dev, unsigned int subdevice,
	unsigned int chan);

/* ALPHA functions */
unsigned int comedi_get_subdevice_flags(comedi_t *dev, unsigned int subdevice);
int comedi_get_len_chanlist(comedi_t *dev, unsigned int subdevice);
int comedi_get_krange(comedi_t *dev, unsigned int subdevice, unsigned int
	chan, unsigned int range, comedi_krange *krange);
unsigned int comedi_get_buf_head_pos(comedi_t *dev, unsigned int subdevice);
int comedi_set_user_int_count(comedi_t *dev, unsigned int subdevice,
	unsigned int buf_user_count);
int comedi_map(comedi_t *dev, unsigned int subdev, void *ptr);
int comedi_unmap(comedi_t *dev, unsigned int subdev);
int comedi_get_buffer_size(comedi_t *dev, unsigned int subdev);
int comedi_mark_buffer_read(comedi_t *dev, unsigned int subdevice,
	unsigned int num_bytes);
int comedi_mark_buffer_written(comedi_t *d, unsigned int subdevice,
	unsigned int num_bytes);
int comedi_get_buffer_contents(comedi_t *dev, unsigned int subdevice);
int comedi_get_buffer_offset(comedi_t *dev, unsigned int subdevice);

#else

/* these functions may not be called at real-time priority */

int comedi_open(unsigned int minor);
void comedi_close(unsigned int minor);

/* these functions may be called at any priority, but may fail at
   real-time priority */

int comedi_lock(unsigned int minor, unsigned int subdev);
int comedi_unlock(unsigned int minor, unsigned int subdev);

/* these functions may be called at any priority, but you must hold
   the lock for the subdevice */

int comedi_cancel(unsigned int minor, unsigned int subdev);
int comedi_register_callback(unsigned int minor, unsigned int subdev,
	unsigned int mask, int (*cb) (unsigned int, void *), void *arg);

int comedi_command(unsigned int minor, comedi_cmd *cmd);
int comedi_command_test(unsigned int minor, comedi_cmd *cmd);
int comedi_trigger(unsigned int minor, unsigned int subdev, comedi_trig *it);
int __comedi_trigger(unsigned int minor, unsigned int subdev, comedi_trig *it);
int comedi_data_write(unsigned int dev, unsigned int subdev, unsigned int chan,
	unsigned int range, unsigned int aref, lsampl_t data);
int comedi_data_read(unsigned int dev, unsigned int subdev, unsigned int chan,
	unsigned int range, unsigned int aref, lsampl_t *data);
int comedi_dio_config(unsigned int dev, unsigned int subdev, unsigned int chan,
	unsigned int io);
int comedi_dio_read(unsigned int dev, unsigned int subdev, unsigned int chan,
	unsigned int *val);
int comedi_dio_write(unsigned int dev, unsigned int subdev, unsigned int chan,
	unsigned int val);
int comedi_dio_bitfield(unsigned int dev, unsigned int subdev,
	unsigned int mask, unsigned int *bits);
int comedi_get_n_subdevices(unsigned int dev);
int comedi_get_version_code(unsigned int dev);
char *comedi_get_driver_name(unsigned int dev);
char *comedi_get_board_name(unsigned int minor);
int comedi_get_subdevice_type(unsigned int minor, unsigned int subdevice);
int comedi_find_subdevice_by_type(unsigned int minor, int type,
	unsigned int subd);
int comedi_get_n_channels(unsigned int minor, unsigned int subdevice);
lsampl_t comedi_get_maxdata(unsigned int minor, unsigned int subdevice, unsigned
	int chan);
int comedi_get_n_ranges(unsigned int minor, unsigned int subdevice, unsigned int
	chan);
int comedi_do_insn(unsigned int minor, comedi_insn *insn);
int comedi_poll(unsigned int minor, unsigned int subdev);

/* DEPRECATED functions */
int comedi_get_rangetype(unsigned int minor, unsigned int subdevice,
	unsigned int chan);

/* ALPHA functions */
unsigned int comedi_get_subdevice_flags(unsigned int minor, unsigned int
	subdevice);
int comedi_get_len_chanlist(unsigned int minor, unsigned int subdevice);
int comedi_get_krange(unsigned int minor, unsigned int subdevice, unsigned int
	chan, unsigned int range, comedi_krange *krange);
unsigned int comedi_get_buf_head_pos(unsigned int minor, unsigned int
	subdevice);
int comedi_set_user_int_count(unsigned int minor, unsigned int subdevice,
	unsigned int buf_user_count);
int comedi_map(unsigned int minor, unsigned int subdev, void **ptr);
int comedi_unmap(unsigned int minor, unsigned int subdev);

#endif

#endif
