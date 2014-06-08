/* Altera TSE SGDMA and MSGDMA Linux driver
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "altera_tse.h"
#include "altera_utils.h"

void tse_set_bit(void __iomem *ioaddr, u32 bit_mask)
{
	u32 value = ioread32(ioaddr);
	value |= bit_mask;
	iowrite32(value, ioaddr);
}

void tse_clear_bit(void __iomem *ioaddr, u32 bit_mask)
{
	u32 value = ioread32(ioaddr);
	value &= ~bit_mask;
	iowrite32(value, ioaddr);
}

int tse_bit_is_set(void __iomem *ioaddr, u32 bit_mask)
{
	u32 value = ioread32(ioaddr);
	return (value & bit_mask) ? 1 : 0;
}

int tse_bit_is_clear(void __iomem *ioaddr, u32 bit_mask)
{
	u32 value = ioread32(ioaddr);
	return (value & bit_mask) ? 0 : 1;
}
