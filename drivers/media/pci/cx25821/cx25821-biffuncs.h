/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 */

#ifndef _BITFUNCS_H
#define _BITFUNCS_H

#define SetBit(Bit)  (1 << Bit)

static inline u8 getBit(u32 sample, u8 index)
{
	return (u8) ((sample >> index) & 1);
}

static inline u32 clearBitAtPos(u32 value, u8 bit)
{
	return value & ~(1 << bit);
}

static inline u32 setBitAtPos(u32 sample, u8 bit)
{
	sample |= (1 << bit);
	return sample;

}

#endif
