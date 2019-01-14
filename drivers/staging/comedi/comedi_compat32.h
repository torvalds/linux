/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * comedi/comedi_compat32.h
 * 32-bit ioctl compatibility for 64-bit comedi kernel module.
 *
 * Author: Ian Abbott, MEV Ltd. <abbotti@mev.co.uk>
 * Copyright (C) 2007 MEV Ltd. <http://www.mev.co.uk/>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2007 David A. Schleef <ds@schleef.org>
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
