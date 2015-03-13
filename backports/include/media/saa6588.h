/*

    Types and defines needed for RDS. This is included by
    saa6588.c and every driver (e.g. bttv-driver.c) that wants
    to use the saa6588 module.

    (c) 2005 by Hans J. Koch

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

#ifndef _SAA6588_H
#define _SAA6588_H

struct saa6588_command {
	unsigned int  block_count;
	bool          nonblocking;
	int           result;
	unsigned char __user *buffer;
	struct file   *instance;
	poll_table    *event_list;
};

/* These ioctls are internal to the kernel */
#define SAA6588_CMD_CLOSE	_IOW('R', 2, int)
#define SAA6588_CMD_READ	_IOR('R', 3, int)
#define SAA6588_CMD_POLL	_IOR('R', 4, int)

#endif
