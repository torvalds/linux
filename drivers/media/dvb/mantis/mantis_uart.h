/*
	Mantis PCI bridge driver

	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

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

#ifndef __MANTIS_UART_H
#define __MANTIS_UART_H

enum mantis_baud {
	MANTIS_BAUD_9600	= 0,
	MANTIS_BAUD_19200,
	MANTIS_BAUD_38400,
	MANTIS_BAUD_57600,
	MANTIS_BAUD_115200
};

enum mantis_parity {
	MANTIS_PARITY_NONE	= 0,
	MANTIS_PARITY_EVEN,
	MANTIS_PARITY_ODD
};

#endif // __MANTIS_UART_H
