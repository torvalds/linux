// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi/comedi_compat32.h
 * 32-bit ioctl compatibility for 64-bit comedi kernel module.
 *
 * Author: Ian Abbott, MEV Ltd. <abbotti@mev.co.uk>
 * Copyright (C) 2007 MEV Ltd. <http://www.mev.co.uk/>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2007 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _COMEDI_COMPAT32_H
#define _COMEDI_COMPAT32_H

#ifdef CONFIG_COMPAT

struct file;
long comedi_compat_ioctl(struct file *file, unsigned int cmd,
			 unsigned long arg);

#else /* CONFIG_COMPAT */

#define comedi_compat_ioctl	NULL

#endif /* CONFIG_COMPAT */

#endif /* _COMEDI_COMPAT32_H */
