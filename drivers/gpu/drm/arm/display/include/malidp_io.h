/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _MALIDP_IO_H_
#define _MALIDP_IO_H_

#include <linux/io.h>

static inline u32
malidp_read32(u32 __iomem *base, u32 offset)
{
	return readl((base + (offset >> 2)));
}

static inline void
malidp_write32(u32 __iomem *base, u32 offset, u32 v)
{
	writel(v, (base + (offset >> 2)));
}

static inline void
malidp_write32_mask(u32 __iomem *base, u32 offset, u32 m, u32 v)
{
	u32 tmp = malidp_read32(base, offset);

	tmp &= (~m);
	malidp_write32(base, offset, v | tmp);
}

static inline void
malidp_write_group(u32 __iomem *base, u32 offset, int num, const u32 *values)
{
	int i;

	for (i = 0; i < num; i++)
		malidp_write32(base, offset + i * 4, values[i]);
}

#endif /*_MALIDP_IO_H_*/
