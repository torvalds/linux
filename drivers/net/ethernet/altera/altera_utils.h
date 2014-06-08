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

#include <linux/kernel.h>

#ifndef __ALTERA_UTILS_H__
#define __ALTERA_UTILS_H__

void tse_set_bit(void __iomem *ioaddr, u32 bit_mask);
void tse_clear_bit(void __iomem *ioaddr, u32 bit_mask);
int tse_bit_is_set(void __iomem *ioaddr, u32 bit_mask);
int tse_bit_is_clear(void __iomem *ioaddr, u32 bit_mask);

#endif /* __ALTERA_UTILS_H__*/
