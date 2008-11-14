/*
    comedi/comedi_compat32.h
    32-bit ioctl compatibility for 64-bit comedi kernel module.

    Author: Ian Abbott, MEV Ltd. <abbotti@mev.co.uk>
    Copyright (C) 2007 MEV Ltd. <http://www.mev.co.uk/>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2007 David A. Schleef <ds@schleef.org>

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

#ifndef _COMEDI_COMPAT32_H
#define _COMEDI_COMPAT32_H

#include <linux/compat.h>
#include <linux/fs.h>	/* For HAVE_COMPAT_IOCTL and HAVE_UNLOCKED_IOCTL */

#ifdef CONFIG_COMPAT

#ifdef HAVE_COMPAT_IOCTL

extern long comedi_compat_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg);
#define comedi_register_ioctl32() do {} while (0)
#define comedi_unregister_ioctl32() do {} while (0)

#else /* HAVE_COMPAT_IOCTL */

#define comedi_compat_ioctl 0	/* NULL */
extern void comedi_register_ioctl32(void);
extern void comedi_unregister_ioctl32(void);

#endif /* HAVE_COMPAT_IOCTL */

#else /* CONFIG_COMPAT */

#define comedi_compat_ioctl 0	/* NULL */
#define comedi_register_ioctl32() do {} while (0)
#define comedi_unregister_ioctl32() do {} while (0)

#endif /* CONFIG_COMPAT */

#endif /* _COMEDI_COMPAT32_H */
