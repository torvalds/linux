/*
 *  arch/arm/mach-mxs/include/mach/uncompress.h
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) Shane Nay (shane@minirl.com)
 *  Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __MACH_MXS_UNCOMPRESS_H__
#define __MACH_MXS_UNCOMPRESS_H__

unsigned long mxs_duart_base;

#define MXS_DUART(x)	(*(volatile unsigned long *)(mxs_duart_base + (x)))

#define MXS_DUART_DR		0x00
#define MXS_DUART_FR		0x18
#define MXS_DUART_FR_TXFE	(1 << 7)
#define MXS_DUART_CR		0x30
#define MXS_DUART_CR_UARTEN	(1 << 0)

/*
 * The following code assumes the serial port has already been
 * initialized by the bootloader. If it's not, the output is
 * simply discarded.
 */

static void putc(int ch)
{
	if (!mxs_duart_base)
		return;
	if (!(MXS_DUART(MXS_DUART_CR) & MXS_DUART_CR_UARTEN))
		return;

	while (!(MXS_DUART(MXS_DUART_FR) & MXS_DUART_FR_TXFE))
		barrier();

	MXS_DUART(MXS_DUART_DR) = ch;
}

static inline void flush(void)
{
}

#define MX23_DUART_BASE_ADDR	0x80070000
#define MX28_DUART_BASE_ADDR	0x80074000
#define MXS_DIGCTL_CHIPID	0x8001c310

static inline void __arch_decomp_setup(unsigned long arch_id)
{
	u16 chipid = (*(volatile unsigned long *) MXS_DIGCTL_CHIPID) >> 16;

	switch (chipid) {
	case 0x3780:
		mxs_duart_base = MX23_DUART_BASE_ADDR;
		break;
	case 0x2800:
		mxs_duart_base = MX28_DUART_BASE_ADDR;
		break;
	default:
		break;
	}
}

#define arch_decomp_setup()	__arch_decomp_setup(arch_id)
#define arch_decomp_wdog()

#endif /* __MACH_MXS_UNCOMPRESS_H__ */
