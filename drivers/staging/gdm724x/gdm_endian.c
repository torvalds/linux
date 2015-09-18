/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include "gdm_endian.h"

void gdm_set_endian(struct gdm_endian *ed, u8 dev_endian)
{
	if (dev_endian == ENDIANNESS_BIG)
		ed->dev_ed = ENDIANNESS_BIG;
	else
		ed->dev_ed = ENDIANNESS_LITTLE;
}

u16 gdm_cpu_to_dev16(struct gdm_endian *ed, u16 x)
{
	if (ed->dev_ed == ENDIANNESS_LITTLE)
		return cpu_to_le16(x);
	else
		return cpu_to_be16(x);
}

u16 gdm_dev16_to_cpu(struct gdm_endian *ed, u16 x)
{
	if (ed->dev_ed == ENDIANNESS_LITTLE)
		return le16_to_cpu(x);
	else
		return be16_to_cpu(x);
}

u32 gdm_cpu_to_dev32(struct gdm_endian *ed, u32 x)
{
	if (ed->dev_ed == ENDIANNESS_LITTLE)
		return cpu_to_le32(x);
	else
		return cpu_to_be32(x);
}

u32 gdm_dev32_to_cpu(struct gdm_endian *ed, u32 x)
{
	if (ed->dev_ed == ENDIANNESS_LITTLE)
		return le32_to_cpu(x);
	else
		return be32_to_cpu(x);
}
