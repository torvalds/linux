/*
    module/exp_ioctl.c
    exported comedi functions

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>

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

#define __NO_VERSION__
#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#include "comedidev.h"

/* for drivers */
EXPORT_SYMBOL(comedi_driver_register);
EXPORT_SYMBOL(comedi_driver_unregister);
/* EXPORT_SYMBOL(comedi_bufcheck); */
/* EXPORT_SYMBOL(comedi_done); */
/* EXPORT_SYMBOL(comedi_error_done); */
EXPORT_SYMBOL(comedi_error);
/* EXPORT_SYMBOL(comedi_eobuf); */
/* EXPORT_SYMBOL(comedi_eos); */
EXPORT_SYMBOL(comedi_event);
EXPORT_SYMBOL(comedi_get_subdevice_runflags);
EXPORT_SYMBOL(comedi_set_subdevice_runflags);
EXPORT_SYMBOL(range_bipolar10);
EXPORT_SYMBOL(range_bipolar5);
EXPORT_SYMBOL(range_bipolar2_5);
EXPORT_SYMBOL(range_unipolar10);
EXPORT_SYMBOL(range_unipolar5);
EXPORT_SYMBOL(range_unknown);
#ifdef CONFIG_COMEDI_RT
EXPORT_SYMBOL(comedi_free_irq);
EXPORT_SYMBOL(comedi_request_irq);
EXPORT_SYMBOL(comedi_switch_to_rt);
EXPORT_SYMBOL(comedi_switch_to_non_rt);
EXPORT_SYMBOL(rt_pend_call);
#endif
#ifdef CONFIG_COMEDI_DEBUG
EXPORT_SYMBOL(comedi_debug);
#endif
EXPORT_SYMBOL_GPL(comedi_alloc_board_minor);
EXPORT_SYMBOL_GPL(comedi_free_board_minor);
EXPORT_SYMBOL_GPL(comedi_pci_auto_config);
EXPORT_SYMBOL_GPL(comedi_pci_auto_unconfig);
EXPORT_SYMBOL_GPL(comedi_usb_auto_config);
EXPORT_SYMBOL_GPL(comedi_usb_auto_unconfig);

/* for kcomedilib */
EXPORT_SYMBOL(check_chanlist);
EXPORT_SYMBOL_GPL(comedi_get_device_file_info);

EXPORT_SYMBOL(comedi_buf_put);
EXPORT_SYMBOL(comedi_buf_get);
EXPORT_SYMBOL(comedi_buf_read_n_available);
EXPORT_SYMBOL(comedi_buf_write_free);
EXPORT_SYMBOL(comedi_buf_write_alloc);
EXPORT_SYMBOL(comedi_buf_read_free);
EXPORT_SYMBOL(comedi_buf_read_alloc);
EXPORT_SYMBOL(comedi_buf_memcpy_to);
EXPORT_SYMBOL(comedi_buf_memcpy_from);
EXPORT_SYMBOL(comedi_reset_async_buf);
