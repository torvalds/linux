// SPDX-License-Identifier: GPL-2.0-only
/* Altera TSE SGDMA and MSGDMA Linux driver
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 */

#include "altera_tse.h"
#include "altera_utils.h"

void tse_set_bit(void __iomem *ioaddr, size_t offs, u32 bit_mask)
{
	u32 value = csrrd32(ioaddr, offs);
	value |= bit_mask;
	csrwr32(value, ioaddr, offs);
}

void tse_clear_bit(void __iomem *ioaddr, size_t offs, u32 bit_mask)
{
	u32 value = csrrd32(ioaddr, offs);
	value &= ~bit_mask;
	csrwr32(value, ioaddr, offs);
}

int tse_bit_is_set(void __iomem *ioaddr, size_t offs, u32 bit_mask)
{
	u32 value = csrrd32(ioaddr, offs);
	return (value & bit_mask) ? 1 : 0;
}

int tse_bit_is_clear(void __iomem *ioaddr, size_t offs, u32 bit_mask)
{
	u32 value = csrrd32(ioaddr, offs);
	return (value & bit_mask) ? 0 : 1;
}
