/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/mach/bitfield.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
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
