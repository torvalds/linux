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

#include "comedidev.h"

/* for drivers */
EXPORT_SYMBOL_GPL(comedi_pci_auto_config);
EXPORT_SYMBOL_GPL(comedi_pci_auto_unconfig);
EXPORT_SYMBOL_GPL(comedi_usb_auto_config);
EXPORT_SYMBOL_GPL(comedi_usb_auto_unconfig);

/* for kcomedilib */
EXPORT_SYMBOL(check_chanlist);
