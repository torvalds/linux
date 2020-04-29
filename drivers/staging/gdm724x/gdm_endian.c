// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved. */

#include <linux/kernel.h>
#include "gdm_endian.h"

__dev16 gdm_cpu_to_dev16(u8 dev_ed, u16 x)
{
	if (dev_ed == ENDIANNESS_LITTLE)
		return (__force __dev16)cpu_to_le16(x);
	else
		return (__force __dev16)cpu_to_be16(x);
}

u16 gdm_dev16_to_cpu(u8 dev_ed, __dev16 x)
{
	if (dev_ed == ENDIANNESS_LITTLE)
		return le16_to_cpu((__force __le16)x);
	else
		return be16_to_cpu((__force __be16)x);
}

__dev32 gdm_cpu_to_dev32(u8 dev_ed, u32 x)
{
	if (dev_ed == ENDIANNESS_LITTLE)
		return (__force __dev32)cpu_to_le32(x);
	else
		return (__force __dev32)cpu_to_be32(x);
}

u32 gdm_dev32_to_cpu(u8 dev_ed, __dev32 x)
{
	if (dev_ed == ENDIANNESS_LITTLE)
		return le32_to_cpu((__force __le32)x);
	else
		return be32_to_cpu((__force __be32)x);
}
