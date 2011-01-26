/*
 *  Driver for the Conexant CX23885/7/8 PCIe bridge
 *
 *  Various common ioctl() support functions
 *
 *  Copyright (c) 2009 Andy Walls <awalls@md.metrocast.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _CX23885_IOCTL_H_
#define _CX23885_IOCTL_H_

int cx23885_g_chip_ident(struct file *file, void *fh,
			 struct v4l2_dbg_chip_ident *chip);

#ifdef CONFIG_VIDEO_ADV_DEBUG
int cx23885_g_register(struct file *file, void *fh,
		       struct v4l2_dbg_register *reg);


int cx23885_s_register(struct file *file, void *fh,
		       struct v4l2_dbg_register *reg);

#endif
#endif
