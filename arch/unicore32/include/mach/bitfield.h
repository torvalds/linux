/*
 * linux/arch/unicore32/include/mach/bitfield.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __MACH_PUV3_BITFIELD_H__
#define __MACH_PUV3_BITFIELD_H__

#ifndef __ASSEMBLY__
#define UData(Data)	((unsigned long) (Data))
#else
#define UData(Data)	(Data)
#endif

#define FIELD(val, vmask, vshift)	(((val) & ((UData(1) << (vmask)) - 1)) << (vshift))
#define FMASK(vmask, vshift)		(((UData(1) << (vmask)) - 1) << (vshift))

#endif /* __MACH_PUV3_BITFIELD_H__ */
