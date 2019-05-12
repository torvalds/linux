/*
 * platform/serial.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Tensilica Inc.
 */

#ifndef _XTENSA_XT2000_SERIAL_H
#define _XTENSA_XT2000_SERIAL_H

#include <asm/core.h>
#include <asm/io.h>

/*  National-Semi PC16552D DUART:  */

#define DUART16552_1_INTNUM	XCHAL_EXTINT4_NUM
#define DUART16552_2_INTNUM	XCHAL_EXTINT5_NUM

#define DUART16552_1_ADDR	IOADDR(0x0d050020)	/* channel 1 */
#define DUART16552_2_ADDR	IOADDR(0x0d050000)	/* channel 2 */

#define DUART16552_XTAL_FREQ	18432000	/* crystal frequency in Hz */
#define BASE_BAUD ( DUART16552_XTAL_FREQ / 16 )

#endif /* _XTENSA_XT2000_SERIAL_H */
