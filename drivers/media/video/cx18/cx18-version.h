/*
 *  cx18 driver version information
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#ifndef CX18_VERSION_H
#define CX18_VERSION_H

#define CX18_DRIVER_NAME "cx18"
#define CX18_DRIVER_VERSION_MAJOR 1
#define CX18_DRIVER_VERSION_MINOR 2
#define CX18_DRIVER_VERSION_PATCHLEVEL 0

#define CX18_VERSION __stringify(CX18_DRIVER_VERSION_MAJOR) "." __stringify(CX18_DRIVER_VERSION_MINOR) "." __stringify(CX18_DRIVER_VERSION_PATCHLEVEL)
#define CX18_DRIVER_VERSION KERNEL_VERSION(CX18_DRIVER_VERSION_MAJOR, \
	CX18_DRIVER_VERSION_MINOR, CX18_DRIVER_VERSION_PATCHLEVEL)

#endif
