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

/* these functions may not be called at real-time priority */

void *comedi_open(const char *path);
int comedi_close(void *dev);

/* these functions may be called at any priority, but may fail at
   real-time priority */

int comedi_lock(void *dev, unsigned int subdev);
int comedi_unlock(void *dev, unsigned int subdev);

/* these functions may be called at any priority, but you must hold
   the lock for the subdevice */

int comedi_loglevel(int loglevel);
void comedi_perror(const char *s);
char *comedi_strerror(int errnum);
int comedi_errno(void);
int comedi_fileno(void *dev);

int comedi_cancel(void *dev, unsigned int subdev);
int comedi_register_callback(void *dev, unsigned int subdev,
	unsigned int mask, int (*cb) (unsigned int, void *), void *arg);

int comedi_command(void *dev, struct comedi_cmd *cmd);
int comedi_command_test(void *dev, struct comedi_cmd *cmd);
int comedi_trigger(void *dev, unsigned int subdev, struct comedi_trig *it);
int __comedi_trigger(void *dev, unsigned int subdev, struct comedi_trig *it);
int comedi_data_write(void *dev, unsigned int subdev, unsigned int chan,
	unsigned int range, unsigned int aref, unsigned int data);
int comedi_data_read(void *dev, unsigned int subdev, unsigned int chan,
	unsigned int range, unsigned int aref, unsigned int *data);
int comedi_data_read_hint(void *dev, unsigned int subdev,
	unsigned int chan, unsigned int range, unsigned int aref);
int comedi_data_read_delayed(void *dev, unsigned int subdev,
	unsigned int chan, unsigned int range, unsigned int aref,
	unsigned int *data, unsigned int nano_sec);
int comedi_dio_config(void *dev, unsigned int subdev, unsigned int chan,
	unsigned int io);
int comedi_dio_read(void *dev, unsigned int subdev, unsigned int chan,
	unsigned int *val);
int comedi_dio_write(void *dev, unsigned int subdev, unsigned int chan,
	unsigned int val);
int comedi_dio_bitfield(void *dev, unsigned int subdev, unsigned int mask,
	unsigned int *bits);
int comedi_get_n_subdevices(void *dev);
int comedi_get_version_code(void *dev);
const char *comedi_get_driver_name(void *dev);
const char *comedi_get_board_name(void *dev);
int comedi_get_subdevice_type(void *dev, unsigned int subdevice);
int comedi_find_subdevice_by_type(void *dev, int type, unsigned int subd);
int comedi_get_n_channels(void *dev, unsigned int subdevice);
unsigned int comedi_get_maxdata(void *dev, unsigned int subdevice, unsigned
	int chan);
int comedi_get_n_ranges(void *dev, unsigned int subdevice, unsigned int
	chan);
int comedi_do_insn(void *dev, struct comedi_insn *insn);
int comedi_poll(void *dev, unsigned int subdev);

/* DEPRECATED functions */
int comedi_get_rangetype(void *dev, unsigned int subdevice,
	unsigned int chan);

/* ALPHA functions */
unsigned int comedi_get_subdevice_flags(void *dev, unsigned int subdevice);
int comedi_get_len_chanlist(void *dev, unsigned int subdevice);
int comedi_get_krange(void *dev, unsigned int subdevice, unsigned int
	chan, unsigned int range, struct comedi_krange *krange);
unsigned int comedi_get_buf_head_pos(void *dev, unsigned int subdevice);
int comedi_set_user_int_count(void *dev, unsigned int subdevice,
	unsigned int buf_user_count);
int comedi_map(void *dev, unsigned int subdev, void *ptr);
int comedi_unmap(void *dev, unsigned int subdev);
int comedi_get_buffer_size(void *dev, unsigned int subdev);
int comedi_mark_buffer_read(void *dev, unsigned int subdevice,
	unsigned int num_bytes);
int comedi_mark_buffer_written(void *d, unsigned int subdevice,
	unsigned int num_bytes);
int comedi_get_buffer_contents(void *dev, unsigned int subdevice);
int comedi_get_buffer_offset(void *dev, unsigned int subdevice);

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

int comedi_command(unsigned int minor, struct comedi_cmd *cmd);
int comedi_command_test(unsigned int minor, struct comedi_cmd *cmd);
int comedi_trigger(unsigned int minor, unsigned int subdev, struct comedi_trig *it);
int __comedi_trigger(unsigned int minor, unsigned int subdev, struct comedi_trig *it);
int comedi_data_write(unsigned int dev, unsigned int subdev, unsigned int chan,
	unsigned int range, unsigned int aref, unsigned int data);
int comedi_data_read(unsigned int dev, unsigned int subdev, unsigned int chan,
	unsigned int range, unsigned int aref, unsigned int *data);
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
unsigned int comedi_get_maxdata(unsigned int minor, unsigned int subdevice, unsigned
	int chan);
int comedi_get_n_ranges(unsigned int minor, unsigned int subdevice, unsigned int
	chan);
int comedi_do_insn(unsigned int minor, struct comedi_insn *insn);
int comedi_poll(unsigned int minor, unsigned int subdev);

/* DEPRECATED functions */
int comedi_get_rangetype(unsigned int minor, unsigned int subdevice,
	unsigned int chan);

/* ALPHA functions */
unsigned int comedi_get_subdevice_flags(unsigned int minor, unsigned int
	subdevice);
int comedi_get_len_chanlist(unsigned int minor, unsigned int subdevice);
int comedi_get_krange(unsigned int minor, unsigned int subdevice, unsigned int
	chan, unsigned int range, struct comedi_krange *krange);
unsigned int comedi_get_buf_head_pos(unsigned int minor, unsigned int
	subdevice);
int comedi_set_user_int_count(unsigned int minor, unsigned int subdevice,
	unsigned int buf_user_count);
int comedi_map(unsigned int minor, unsigned int subdev, void **ptr);
int comedi_unmap(unsigned int minor, unsigned int subdev);

#endif

#endif
