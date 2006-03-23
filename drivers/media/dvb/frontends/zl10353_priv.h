/*
 *  Driver for Zarlink DVB-T ZL10353 demodulator
 *
 *  Copyright (C) 2006 Christopher Pascoe <c.pascoe@itee.uq.edu.au>
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#ifndef _ZL10353_PRIV_
#define _ZL10353_PRIV_

#define ID_ZL10353	0x14

enum zl10353_reg_addr {
	INTERRUPT_0	= 0x00,
	INTERRUPT_1	= 0x01,
	INTERRUPT_2	= 0x02,
	INTERRUPT_3	= 0x03,
	INTERRUPT_4	= 0x04,
	INTERRUPT_5	= 0x05,
	STATUS_6	= 0x06,
	STATUS_7	= 0x07,
	STATUS_8	= 0x08,
	STATUS_9	= 0x09,
	SNR		= 0x10,
	CHIP_ID		= 0x7F,
};

#endif                          /* _ZL10353_PRIV_ */
