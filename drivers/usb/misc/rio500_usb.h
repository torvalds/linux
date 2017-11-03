// SPDX-License-Identifier: GPL-2.0+
/*  ----------------------------------------------------------------------

    Copyright (C) 2000  Cesar Miquel  (miquel@df.uba.ar)

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

    ---------------------------------------------------------------------- */



#define RIO_SEND_COMMAND			0x1
#define RIO_RECV_COMMAND			0x2

#define RIO_DIR_OUT               	        0x0
#define RIO_DIR_IN				0x1

struct RioCommand {
	short length;
	int request;
	int requesttype;
	int value;
	int index;
	void __user *buffer;
	int timeout;
};
