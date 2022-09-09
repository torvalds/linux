/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * altera-exprt.h
 *
 * altera FPGA driver
 *
 * Copyright (C) Altera Corporation 1998-2001
 * Copyright (C) 2010 NetUP Inc.
 * Copyright (C) 2010 Igor M. Liplianin <liplianin@netup.ru>
 */

#ifndef ALTERA_EXPRT_H
#define ALTERA_EXPRT_H


u32 altera_shrink(u8 *in, u32 in_length, u8 *out, u32 out_length, s32 version);
int netup_jtag_io_lpt(void *device, int tms, int tdi, int read_tdo);

#endif /* ALTERA_EXPRT_H */
