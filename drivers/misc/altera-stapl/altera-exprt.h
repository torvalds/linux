/*
 * altera-exprt.h
 *
 * altera FPGA driver
 *
 * Copyright (C) Altera Corporation 1998-2001
 * Copyright (C) 2010 NetUP Inc.
 * Copyright (C) 2010 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef ALTERA_EXPRT_H
#define ALTERA_EXPRT_H


u32 altera_shrink(u8 *in, u32 in_length, u8 *out, u32 out_length, s32 version);
int netup_jtag_io_lpt(void *device, int tms, int tdi, int read_tdo);

#endif /* ALTERA_EXPRT_H */
