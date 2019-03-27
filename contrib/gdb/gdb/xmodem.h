/* XMODEM support for GDB, the GNU debugger.
   Copyright 1995, 2000 Free Software Foundation, Inc.

   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

struct serial;

int xmodem_init_xfer (struct serial *desc);
void send_xmodem_packet (struct serial *desc, unsigned char *packet, int len,
			 int hashmark);
void xmodem_finish_xfer (struct serial *desc);

#define XMODEM_DATASIZE	128	/* The data size is ALWAYS 128 */
#define XMODEM_1KDATASIZE 1024	/* Unless it's 1024!!! */
#define XMODEM_PACKETSIZE 133	/* data + packet headers and crc */
#define XMODEM_1KPACKETSIZE 1024 + 5	/* data + packet headers and crc */
#define XMODEM_DATAOFFSET 3	/* Offset to start of actual data */
