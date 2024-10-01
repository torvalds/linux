/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved. */

#ifndef __GDM_ENDIAN_H__
#define __GDM_ENDIAN_H__

#include <linux/types.h>

/*
 * For data in "device-endian" byte order (device endianness is model
 * dependent).  Analogous to __leXX or __beXX.
 */
typedef __u32 __bitwise __dev32;
typedef __u16 __bitwise __dev16;

enum {
	ENDIANNESS_MIN = 0,
	ENDIANNESS_UNKNOWN,
	ENDIANNESS_LITTLE,
	ENDIANNESS_BIG,
	ENDIANNESS_MIDDLE,
	ENDIANNESS_MAX
};

__dev16 gdm_cpu_to_dev16(u8 dev_ed, u16 x);
u16 gdm_dev16_to_cpu(u8 dev_ed, __dev16 x);
__dev32 gdm_cpu_to_dev32(u8 dev_ed, u32 x);
u32 gdm_dev32_to_cpu(u8 dev_ed, __dev32 x);

#endif /*__GDM_ENDIAN_H__*/
